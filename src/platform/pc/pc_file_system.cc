#include "platform/pc/pc_file_system.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace minicraft {
namespace platform {
namespace pc {

namespace {

fs::path ToPath(std::string_view sv) {
  return fs::path(std::string(sv));
}

}  // namespace

std::string PcFileSystem::GetDefaultGameDir() const {
#ifdef _WIN32
  const char* appdata = std::getenv("APPDATA");
  std::string base = appdata ? appdata : ".";
  return base + "/playminicraft/mods/Minicraft_Plus";
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  std::string base = home ? home : ".";
  return base + "/playminicraft/mods/Minicraft_Plus";
#else
  const char* home = std::getenv("HOME");
  std::string base = home ? home : ".";
  return base + "/.playminicraft/mods/Minicraft_Plus";
#endif
}

bool PcFileSystem::Exists(std::string_view path) const {
  std::error_code ec;
  return fs::exists(ToPath(path), ec);
}

bool PcFileSystem::IsDirectory(std::string_view path) const {
  std::error_code ec;
  return fs::is_directory(ToPath(path), ec);
}

bool PcFileSystem::CreateDirectories(std::string_view path) const {
  std::error_code ec;
  fs::path p = ToPath(path);
  if (fs::exists(p, ec)) {
    return fs::is_directory(p, ec);
  }
  return fs::create_directories(p, ec);
}

bool PcFileSystem::RemoveFile(std::string_view path) const {
  std::error_code ec;
  return fs::remove(ToPath(path), ec);
}

bool PcFileSystem::RemoveDirectoryAll(std::string_view path) const {
  std::error_code ec;
  return fs::remove_all(ToPath(path), ec) > 0;
}

bool PcFileSystem::Rename(std::string_view from, std::string_view to) const {
  std::error_code ec;
  fs::rename(ToPath(from), ToPath(to), ec);
  return !ec;
}

bool PcFileSystem::ReadBinaryFile(std::string_view path,
                                  std::vector<uint8_t>* out) const {
  if (!out) return false;
  std::ifstream in(ToPath(path), std::ios::binary | std::ios::ate);
  if (!in.is_open()) return false;
  std::streamsize sz = in.tellg();
  if (sz < 0) return false;
  out->resize(static_cast<size_t>(sz));
  in.seekg(0, std::ios::beg);
  return sz == 0 ||
         static_cast<bool>(
             in.read(reinterpret_cast<char*>(out->data()), sz));
}

bool PcFileSystem::WriteBinaryFile(std::string_view path, const uint8_t* data,
                                   size_t size) const {
  fs::path p = ToPath(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }
  std::ofstream out(p, std::ios::binary);
  if (!out.is_open()) return false;
  if (size > 0 && data != nullptr) {
    out.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(size));
  }
  return out.good();
}

bool PcFileSystem::ReadTextFile(std::string_view path, std::string* out) const {
  if (!out) return false;
  std::ifstream in(ToPath(path), std::ios::binary | std::ios::ate);
  if (!in.is_open()) return false;
  std::streamsize sz = in.tellg();
  if (sz < 0) return false;
  out->resize(static_cast<size_t>(sz));
  in.seekg(0, std::ios::beg);
  return sz == 0 || static_cast<bool>(in.read(out->data(), sz));
}

bool PcFileSystem::WriteTextFile(std::string_view path,
                                 std::string_view text) const {
  fs::path p = ToPath(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }
  std::ofstream out(p, std::ios::binary);
  if (!out.is_open()) return false;
  if (!text.empty()) {
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }
  return out.good();
}

std::vector<std::string> PcFileSystem::ListDirectory(
    std::string_view path) const {
  std::vector<std::string> result;
  std::error_code ec;
  fs::path dir = ToPath(path);
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
    return result;
  }
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    result.push_back(e.path().filename().string());
  }
  return result;
}

bool PcFileSystem::SaveScreenshotPng(std::string_view path, int32_t width,
                                     int32_t height,
                                     const uint8_t* rgba) const {
  fs::path p = ToPath(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }
  return stbi_write_png(p.string().c_str(), width, height, 4, rgba,
                        width * 4) != 0;
}

bool PcFileSystem::DecodePngFromMemory(const uint8_t* png_bytes, size_t size,
                                       int32_t* out_w, int32_t* out_h,
                                       std::vector<uint8_t>* out_rgba) const {
  if (!png_bytes || size == 0 || !out_w || !out_h || !out_rgba) return false;
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(png_bytes, static_cast<int>(size),
                                          &w, &h, &channels, 4);
  if (!pixels) return false;
  *out_w = w;
  *out_h = h;
  out_rgba->assign(pixels, pixels + static_cast<size_t>(w) * h * 4U);
  stbi_image_free(pixels);
  return true;
}

}  // namespace pc
}  // namespace platform
}  // namespace minicraft
