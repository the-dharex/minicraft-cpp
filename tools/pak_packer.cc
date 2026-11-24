#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/pak/fast_compress.h"
#include "core/pak/pak_format.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

namespace {

struct RawAsset {
  std::string virtual_path;
  std::vector<uint8_t> data;
};

struct DecodedSheet {
  uint8_t category = 0;  // 0=Entity, 1=Gui, 2=Item, 3=Tile, 4=Misc
  std::string key;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t atlas_x = 0;
  uint16_t atlas_y = 0;
  std::vector<uint8_t> rgba;
};

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>* out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    return false;
  }
  std::streamsize size = in.tellg();
  if (size < 0) {
    return false;
  }
  out->resize(static_cast<size_t>(size));
  in.seekg(0, std::ios::beg);
  if (size > 0 && !in.read(reinterpret_cast<char*>(out->data()), size)) {
    return false;
  }
  return true;
}

// Shelf-packs all sprite sheets into a power-of-two width (max 2048) RGBA8 atlas.
bool BuildMegaAtlas(std::vector<DecodedSheet>* sheets,
                    std::vector<uint8_t>* out_atlas_bin,
                    uint16_t* out_w, uint16_t* out_h) {
  // Sort tallest first, then widest, then name for deterministic tight packing.
  std::sort(sheets->begin(), sheets->end(),
            [](const DecodedSheet& a, const DecodedSheet& b) {
              if (a.height != b.height) return a.height > b.height;
              if (a.width != b.width) return a.width > b.width;
              return a.key < b.key;
            });

  constexpr uint16_t kAtlasWidth = 1024;
  uint16_t cur_x = 0;
  uint16_t cur_y = 0;
  uint16_t shelf_h = 0;

  for (DecodedSheet& s : *sheets) {
    if (s.width > kAtlasWidth) {
      std::cerr << "Sheet wider than kAtlasWidth: " << s.key << "\n";
      return false;
    }
    if (cur_x + s.width > kAtlasWidth) {
      cur_x = 0;
      cur_y = static_cast<uint16_t>(cur_y + shelf_h);
      shelf_h = 0;
    }
    s.atlas_x = cur_x;
    s.atlas_y = cur_y;
    cur_x = static_cast<uint16_t>(cur_x + s.width);
    if (s.height > shelf_h) {
      shelf_h = s.height;
    }
  }

  uint16_t atlas_height = static_cast<uint16_t>(cur_y + shelf_h);
  // Round height up to multiple of 8 for clean GPU texture alignment.
  atlas_height = static_cast<uint16_t>((atlas_height + 7U) & ~7U);
  if (atlas_height == 0) {
    atlas_height = 8;
  }

  *out_w = kAtlasWidth;
  *out_h = atlas_height;

  std::vector<uint8_t> pixels(
      static_cast<size_t>(kAtlasWidth) * atlas_height * 4U, 0);

  for (const DecodedSheet& s : *sheets) {
    for (uint16_t row = 0; row < s.height; ++row) {
      size_t dst_offset =
          (static_cast<size_t>(s.atlas_y + row) * kAtlasWidth + s.atlas_x) * 4U;
      size_t src_offset = static_cast<size_t>(row) * s.width * 4U;
      std::memcpy(pixels.data() + dst_offset, s.rgba.data() + src_offset,
                  static_cast<size_t>(s.width) * 4U);
    }
  }

  minicraft::pak::AtlasHeader header{};
  header.magic[0] = 'M';
  header.magic[1] = 'A';
  header.magic[2] = 'T';
  header.magic[3] = 'L';
  header.width = kAtlasWidth;
  header.height = atlas_height;
  header.sheet_count = static_cast<uint32_t>(sheets->size());

  out_atlas_bin->clear();
  out_atlas_bin->reserve(sizeof(header) + sheets->size() * 32 + pixels.size());
  const auto append_bytes = [out_atlas_bin](const void* ptr, size_t n) {
    const auto* p = static_cast<const uint8_t*>(ptr);
    out_atlas_bin->insert(out_atlas_bin->end(), p, p + n);
  };

  append_bytes(&header, sizeof(header));
  for (const DecodedSheet& s : *sheets) {
    minicraft::pak::AtlasSheetEntry entry{};
    entry.category = s.category;
    entry.reserved = 0;
    entry.x = s.atlas_x;
    entry.y = s.atlas_y;
    entry.w = s.width;
    entry.h = s.height;
    entry.name_length = static_cast<uint16_t>(s.key.size());
    append_bytes(&entry, sizeof(entry));
    append_bytes(s.key.data(), s.key.size());
  }
  append_bytes(pixels.data(), pixels.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: pak_packer <resources_dir> <output_pak>\n";
    return 1;
  }

  fs::path input_root = fs::path(argv[1]);
  fs::path output_pak = fs::path(argv[2]);

  if (!fs::exists(input_root) || !fs::is_directory(input_root)) {
    std::cerr << "Input directory does not exist: " << input_root << "\n";
    return 1;
  }

  std::vector<RawAsset> raw_assets;
  std::vector<DecodedSheet> atlas_sheets;
  size_t original_total_bytes = 0;

  for (const fs::directory_entry& dir_entry :
       fs::recursive_directory_iterator(input_root)) {
    if (!dir_entry.is_regular_file()) {
      continue;
    }

    fs::path rel = fs::relative(dir_entry.path(), input_root);
    std::string vpath = rel.generic_string();

    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(dir_entry.path(), &bytes)) {
      std::cerr << "Failed to read file: " << dir_entry.path() << "\n";
      return 1;
    }
    original_total_bytes += bytes.size();

    // Check if this PNG belongs in the pre-baked Mega-Atlas.
    bool is_atlas_png = false;
    uint8_t category = 4;
    std::string sheet_key;

    if (EndsWith(vpath, ".png")) {
      if (StartsWith(vpath, "assets/textures/entity/")) {
        is_atlas_png = true;
        category = 0;
        sheet_key = dir_entry.path().stem().string();
      } else if (StartsWith(vpath, "assets/textures/gui/")) {
        is_atlas_png = true;
        category = 1;
        sheet_key = dir_entry.path().stem().string();
      } else if (StartsWith(vpath, "assets/textures/item/")) {
        is_atlas_png = true;
        category = 2;
        sheet_key = dir_entry.path().stem().string();
      } else if (StartsWith(vpath, "assets/textures/tile/")) {
        is_atlas_png = true;
        category = 3;
        sheet_key = dir_entry.path().stem().string();
      } else if (vpath == "resources/textures/skins.png") {
        is_atlas_png = true;
        category = 4;
        sheet_key = "default_skins";
      } else if (vpath == "resources/logo.png") {
        is_atlas_png = true;
        category = 4;
        sheet_key = "logo";
      } else if (vpath == "pack.png") {
        is_atlas_png = true;
        category = 4;
        sheet_key = "default_pack_logo";
      }
    }

    if (is_atlas_png) {
      int w = 0, h = 0, channels = 0;
      stbi_uc* decoded = stbi_load_from_memory(
          bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
      if (decoded == nullptr) {
        std::cerr << "Failed to decode PNG: " << vpath << "\n";
        return 1;
      }
      DecodedSheet sheet;
      sheet.category = category;
      sheet.key = sheet_key;
      sheet.width = static_cast<uint16_t>(w);
      sheet.height = static_cast<uint16_t>(h);
      sheet.rgba.assign(decoded, decoded + static_cast<size_t>(w) * h * 4U);
      stbi_image_free(decoded);
      atlas_sheets.push_back(std::move(sheet));
    } else {
      raw_assets.push_back({std::move(vpath), std::move(bytes)});
    }
  }

  std::vector<uint8_t> atlas_bin;
  uint16_t atlas_w = 0, atlas_h = 0;
  if (!BuildMegaAtlas(&atlas_sheets, &atlas_bin, &atlas_w, &atlas_h)) {
    return 1;
  }
  raw_assets.push_back({"__atlas__/atlas.bin", std::move(atlas_bin)});

  struct PendingEntry {
    std::string name;
    uint64_t hash;
    std::vector<uint8_t> payload;
    uint32_t raw_size;
    uint8_t codec;
  };

  std::vector<PendingEntry> pending;
  pending.reserve(raw_assets.size());

  for (RawAsset& asset : raw_assets) {
    PendingEntry e;
    e.name = std::move(asset.virtual_path);
    e.hash = minicraft::pak::Fnv1a64(e.name);
    e.raw_size = static_cast<uint32_t>(asset.data.size());

    std::vector<uint8_t> compressed =
        minicraft::pak::CompressFastLz(asset.data.data(), asset.data.size());
    if (!compressed.empty() && compressed.size() < asset.data.size()) {
      e.payload = std::move(compressed);
      e.codec = static_cast<uint8_t>(minicraft::pak::CompressionCodec::kFastLz);
    } else {
      e.payload = std::move(asset.data);
      e.codec = static_cast<uint8_t>(minicraft::pak::CompressionCodec::kNone);
    }
    pending.push_back(std::move(e));
  }

  std::sort(pending.begin(), pending.end(),
            [](const PendingEntry& a, const PendingEntry& b) {
              if (a.hash != b.hash) return a.hash < b.hash;
              return a.name < b.name;
            });

  fs::create_directories(output_pak.parent_path());
  std::ofstream out(output_pak, std::ios::binary);
  if (!out.is_open()) {
    std::cerr << "Failed to open output pak: " << output_pak << "\n";
    return 1;
  }

  minicraft::pak::PakHeader header{};
  std::memcpy(header.magic, minicraft::pak::kPakMagic, 4);
  header.version = minicraft::pak::kPakVersion;
  header.flags = 0;
  header.entry_count = static_cast<uint32_t>(pending.size());
  header.reserved = 0;

  // Reserve space for header.
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));

  std::vector<minicraft::pak::PakTocEntry> toc(pending.size());
  std::string string_table;

  for (size_t i = 0; i < pending.size(); ++i) {
    // Align each data payload to 8 bytes.
    uint64_t cur = static_cast<uint64_t>(out.tellp());
    uint64_t pad = (8U - (cur & 7U)) & 7U;
    if (pad > 0) {
      const char zeros[8] = {};
      out.write(zeros, static_cast<std::streamsize>(pad));
      cur += pad;
    }

    toc[i].name_hash = pending[i].hash;
    toc[i].name_offset = static_cast<uint32_t>(string_table.size());
    toc[i].name_length = static_cast<uint32_t>(pending[i].name.size());
    toc[i].data_offset = cur;
    toc[i].packed_size = static_cast<uint32_t>(pending[i].payload.size());
    toc[i].raw_size = pending[i].raw_size;
    toc[i].codec = pending[i].codec;

    string_table.append(pending[i].name);
    if (!pending[i].payload.empty()) {
      out.write(reinterpret_cast<const char*>(pending[i].payload.data()),
                static_cast<std::streamsize>(pending[i].payload.size()));
    }
  }

  header.toc_offset = static_cast<uint64_t>(out.tellp());
  out.write(reinterpret_cast<const char*>(toc.data()),
            static_cast<std::streamsize>(toc.size() * sizeof(toc[0])));

  header.string_table_offset = static_cast<uint64_t>(out.tellp());
  header.string_table_size = static_cast<uint64_t>(string_table.size());
  out.write(string_table.data(),
            static_cast<std::streamsize>(string_table.size()));

  uint64_t final_size = static_cast<uint64_t>(out.tellp());
  out.seekp(0, std::ios::beg);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.close();

  std::cout << "Packed " << atlas_sheets.size() << " sprite sheets into "
            << atlas_w << "x" << atlas_h << " Mega-Atlas.\n";
  std::cout << "Total entries in .pak: " << pending.size() << "\n";
  std::cout << "Original resource files: " << original_total_bytes
            << " bytes -> assets.pak: " << final_size << " bytes ("
            << (100.0 * final_size / std::max<size_t>(1, original_total_bytes))
            << "%)\n";
  return 0;
}
