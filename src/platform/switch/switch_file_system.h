#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/platform/file_system.h"

namespace minicraft::platform::nx {

// Native Nintendo Switch filesystem backend compliant with Nintendo guidelines:
// - Saves, preferences, and unlocks are stored strictly in console Horizon OS
//   save data (`save:/minicraft_plus`), with zero SD card save fallback/backup.
// - User-modifiable content (`skins/`, `resourcepacks/`, `screenshots/`) is
//   routed transparently to the SD card (`sdmc:/switch/minicraft_plus/...`).
class SwitchFileSystem final : public IFileSystem {
 public:
  SwitchFileSystem() = default;
  ~SwitchFileSystem() override;

  // Initializes account selection (preselected user or native psel applet) and
  // mounts console save data (`save:/`), plus `sdmc:/` for user content only.
  bool InitializeStorage();

  // Commits pending save data changes and unmounts storage devices cleanly.
  void ShutdownStorage();

  // Commits `save:/` if any write/delete/rename occurred since the last commit.
  void CommitIfDirty();

  // Non-blocking per-frame commit: skips if a background worker currently
  // holds `save:/` busy so the render thread never stalls during async saves.
  void TryCommitIfDirty();

  bool IsUsingSaveData() const { return save_mounted_; }
  const std::string& LastStorageError() const { return last_error_; }

  std::string GetDefaultGameDir() const override;
  bool Exists(std::string_view path) const override;
  bool IsDirectory(std::string_view path) const override;
  bool CreateDirectories(std::string_view path) const override;
  bool RemoveFile(std::string_view path) const override;
  bool RemoveDirectoryAll(std::string_view path) const override;
  bool Rename(std::string_view from, std::string_view to) const override;

  bool ReadBinaryFile(std::string_view path,
                      std::vector<uint8_t>* out) const override;
  bool ReadBinaryHeader(std::string_view path, uint8_t* out, size_t max_bytes,
                        size_t* out_bytes_read) const override;
  bool WriteBinaryFile(std::string_view path, const uint8_t* data,
                       size_t size) const override;
  bool ReadTextFile(std::string_view path, std::string* out) const override;
  bool WriteTextFile(std::string_view path,
                     std::string_view content) const override;

  std::vector<std::string> ListDirectory(std::string_view path) const override;

  bool SaveScreenshotPng(std::string_view path, int32_t width, int32_t height,
                         const uint8_t* rgba) const override;
  bool DecodePngFromMemory(const uint8_t* data, size_t size, int32_t* out_w,
                           int32_t* out_h,
                           std::vector<uint8_t>* out_rgba) const override;

 private:
  static std::string NormalizePath(std::string_view path);
  std::string ResolvePath(std::string_view path) const;
  static bool IsSavePath(std::string_view path);
  static bool EnsureParentDirectories(const std::string& path);
  bool RemoveDirectoryRecursiveFallback(const std::string& dir_path) const;
  void MarkDirtyIfSave(std::string_view path) const;

  std::string base_dir_ = "save:/minicraft_plus";
  std::string sdmc_base_dir_ = "sdmc:/switch/minicraft_plus";
  std::string last_error_;
  bool save_mounted_ = false;
  bool sdmc_mounted_ = false;
  mutable std::atomic<bool> dirty_{false};
  mutable std::recursive_mutex save_fs_mutex_;
};

}  // namespace minicraft::platform::nx
