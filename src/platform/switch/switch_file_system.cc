#include "platform/switch/switch_file_system.h"

#include <dirent.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace minicraft::platform::nx {

namespace {

bool CopyAndRemoveFile(const std::string& src, const std::string& dst) {
  std::FILE* in = std::fopen(src.c_str(), "rb");
  if (!in) return false;

  std::FILE* out = std::fopen(dst.c_str(), "wb");
  if (!out) {
    std::fclose(in);
    return false;
  }

  uint8_t buf[16384];
  bool ok = true;
  while (true) {
    const std::size_t n = std::fread(buf, 1, sizeof(buf), in);
    if (n > 0) {
      if (std::fwrite(buf, 1, n, out) != n) {
        ok = false;
        break;
      }
    }
    if (n < sizeof(buf)) {
      if (std::ferror(in)) ok = false;
      break;
    }
  }

  std::fclose(in);
  if (std::fclose(out) != 0) ok = false;
  if (!ok) {
    std::remove(dst.c_str());
    return false;
  }
  return std::remove(src.c_str()) == 0;
}

}  // namespace

SwitchFileSystem::~SwitchFileSystem() {
  ShutdownStorage();
}

bool SwitchFileSystem::InitializeStorage() {
  const AppletType applet_type = appletGetAppletType();
  const bool is_application =
      (applet_type == AppletType_Application ||
       applet_type == AppletType_SystemApplication);

  if (!is_application) {
    last_error_ =
        "Save data requires Application mode (launch via NSP/forwarder or Title Override). "
        "SD card save fallback is disabled per Nintendo Switch guidelines.";
    return false;
  }

  Result acc_rc = accountInitialize(AccountServiceType_Application);
  if (R_FAILED(acc_rc)) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "accountInitialize failed: 0x%08x", acc_rc);
    last_error_ = msg;
    return false;
  }

  AccountUid uid{};
  Result user_rc = accountGetPreselectedUser(&uid);
  if (R_FAILED(user_rc) || !accountUidIsValid(&uid)) {
    PselUserSelectionSettings settings{};
    std::memset(&settings, 0, sizeof(settings));
    user_rc = pselShowUserSelector(&uid, &settings);
  }
  accountExit();

  if (R_FAILED(user_rc) || !accountUidIsValid(&uid)) {
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Failed to select Nintendo Switch user profile (0x%08x)",
                  user_rc);
    last_error_ = msg;
    return false;
  }

  Result mount_rc =
      fsdevMountSaveData("save", FS_SAVEDATA_CURRENT_APPLICATIONID, uid);
  if (R_FAILED(mount_rc)) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "fsdevMountSaveData failed: 0x%08x",
                  mount_rc);
    last_error_ = msg;
    return false;
  }

  save_mounted_ = true;
  base_dir_ = "save:/minicraft_plus";
  CreateDirectories(base_dir_);
  CommitIfDirty();

  // Mount SD card solely for user-supplied content (skins, resourcepacks,
  // screenshots).
  if (R_SUCCEEDED(fsdevMountSdmc())) {
    sdmc_mounted_ = true;
    CreateDirectories(sdmc_base_dir_ + "/skins");
    CreateDirectories(sdmc_base_dir_ + "/resourcepacks");
  }

  return true;
}

void SwitchFileSystem::ShutdownStorage() {
  std::lock_guard<std::recursive_mutex> lock(save_fs_mutex_);
  CommitIfDirty();
  if (save_mounted_) {
    fsdevUnmountDevice("save");
    save_mounted_ = false;
  }
  if (sdmc_mounted_) {
    fsdevUnmountDevice("sdmc");
    sdmc_mounted_ = false;
  }
}

void SwitchFileSystem::CommitIfDirty() {
  if (!save_mounted_) return;
  if (!dirty_.load(std::memory_order_acquire)) return;

  std::lock_guard<std::recursive_mutex> lock(save_fs_mutex_);
  if (!dirty_.exchange(false, std::memory_order_acq_rel)) return;
  Result rc = fsdevCommitDevice("save");
  if (R_FAILED(rc)) {
    std::fprintf(stderr, "SwitchFileSystem: fsdevCommitDevice failed: 0x%08x\n",
                 rc);
  }
}

void SwitchFileSystem::TryCommitIfDirty() {
  if (!save_mounted_) return;
  if (!dirty_.load(std::memory_order_acquire)) return;

  std::unique_lock<std::recursive_mutex> lock(save_fs_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return;
  if (!dirty_.exchange(false, std::memory_order_acq_rel)) return;
  Result rc = fsdevCommitDevice("save");
  if (R_FAILED(rc)) {
    std::fprintf(stderr, "SwitchFileSystem: fsdevCommitDevice failed: 0x%08x\n",
                 rc);
  }
}

std::string SwitchFileSystem::NormalizePath(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (char ch : path) {
    out.push_back(ch == '\\' ? '/' : ch);
  }
  // Keep "<device>:/" intact, otherwise trim trailing slashes.
  while (out.size() > 1 && out.back() == '/') {
    if (out.size() >= 2 && out[out.size() - 2] == ':') {
      break;
    }
    out.pop_back();
  }
  return out;
}

std::string SwitchFileSystem::ResolvePath(std::string_view path) const {
  std::string norm = NormalizePath(path);
  if (norm.empty()) return norm;

  // Route user-customizable assets (skins, resourcepacks, screenshots) to SD.
  if (norm.rfind(base_dir_, 0) == 0 && norm.size() > base_dir_.size() &&
      norm[base_dir_.size()] == '/') {
    const std::string_view rel(norm.data() + base_dir_.size() + 1,
                               norm.size() - base_dir_.size() - 1);
    auto matches_subdir = [&](std::string_view sub) {
      return rel == sub ||
             (rel.size() > sub.size() && rel.rfind(sub, 0) == 0 &&
              rel[sub.size()] == '/');
    };
    if (matches_subdir("skins") || matches_subdir("resourcepacks") ||
        matches_subdir("screenshots")) {
      return sdmc_base_dir_ + "/" + std::string(rel);
    }
  }
  return norm;
}

bool SwitchFileSystem::IsSavePath(std::string_view path) {
  return path.rfind("save:/", 0) == 0;
}

void SwitchFileSystem::MarkDirtyIfSave(std::string_view path) const {
  if (save_mounted_ && IsSavePath(path)) {
    dirty_.store(true, std::memory_order_release);
  }
}

bool SwitchFileSystem::EnsureParentDirectories(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return true;
  if (slash > 0 && path[slash - 1] == ':') return true;

  std::string parent = path.substr(0, slash);
  struct stat st{};
  if (::stat(parent.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }

  std::size_t pos = 0;
  const std::size_t scheme = parent.find(":/");
  if (scheme != std::string::npos) {
    pos = scheme + 2;
  } else if (!parent.empty() && parent[0] == '/') {
    pos = 1;
  }

  while (pos <= parent.size()) {
    const std::size_t next = parent.find('/', pos);
    const std::string sub =
        (next == std::string::npos) ? parent : parent.substr(0, next);
    if (!sub.empty()) {
      if (::mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST) {
        return false;
      }
    }
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return true;
}

std::string SwitchFileSystem::GetDefaultGameDir() const {
  return base_dir_;
}

bool SwitchFileSystem::Exists(std::string_view path) const {
  const std::string norm = ResolvePath(path);
  if (norm.empty()) return false;
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  struct stat st{};
  return ::stat(norm.c_str(), &st) == 0;
}

bool SwitchFileSystem::IsDirectory(std::string_view path) const {
  const std::string norm = ResolvePath(path);
  if (norm.empty()) return false;
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  struct stat st{};
  if (::stat(norm.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

bool SwitchFileSystem::CreateDirectories(std::string_view path) const {
  const std::string norm = ResolvePath(path);
  if (norm.empty()) return false;

  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();

  struct stat st{};
  if (::stat(norm.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }

  std::size_t pos = 0;
  const std::size_t scheme = norm.find(":/");
  if (scheme != std::string::npos) {
    pos = scheme + 2;
  } else if (norm[0] == '/') {
    pos = 1;
  }

  while (pos <= norm.size()) {
    const std::size_t next = norm.find('/', pos);
    const std::string sub =
        (next == std::string::npos) ? norm : norm.substr(0, next);
    if (!sub.empty()) {
      if (::stat(sub.c_str(), &st) != 0) {
        if (::mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST) {
          return false;
        }
        MarkDirtyIfSave(norm);
      } else if (!S_ISDIR(st.st_mode)) {
        return false;
      }
    }
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return true;
}

bool SwitchFileSystem::RemoveFile(std::string_view path) const {
  const std::string norm = ResolvePath(path);
  if (norm.empty()) return false;
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  if (::unlink(norm.c_str()) == 0) {
    MarkDirtyIfSave(norm);
    return true;
  }
  return errno == ENOENT;
}

bool SwitchFileSystem::RemoveDirectoryRecursiveFallback(
    const std::string& dir_path) const {
  DIR* dir = ::opendir(dir_path.c_str());
  if (!dir) return errno == ENOENT;

  bool ok = true;
  struct dirent* ent = nullptr;
  while ((ent = ::readdir(dir)) != nullptr) {
    if (std::strcmp(ent->d_name, ".") == 0 ||
        std::strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    const std::string child = dir_path + "/" + ent->d_name;
    struct stat st{};
    if (::stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      if (!RemoveDirectoryRecursiveFallback(child)) ok = false;
    } else {
      if (::unlink(child.c_str()) != 0) ok = false;
    }
  }
  ::closedir(dir);

  if (::rmdir(dir_path.c_str()) != 0) ok = false;
  return ok;
}

bool SwitchFileSystem::RemoveDirectoryAll(std::string_view path) const {
  const std::string norm = ResolvePath(path);
  if (norm.empty()) return false;
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  if (!Exists(norm)) return true;

  if (!IsDirectory(norm)) {
    return RemoveFile(norm);
  }

  Result rc = fsdevDeleteDirectoryRecursively(norm.c_str());
  if (R_SUCCEEDED(rc)) {
    MarkDirtyIfSave(norm);
    return true;
  }

  if (RemoveDirectoryRecursiveFallback(norm)) {
    MarkDirtyIfSave(norm);
    return true;
  }
  return false;
}

bool SwitchFileSystem::Rename(std::string_view from,
                              std::string_view to) const {
  const std::string src = ResolvePath(from);
  const std::string dst = ResolvePath(to);
  if (src == dst) return true;
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(src) || IsSavePath(dst)) save_lock.lock();
  if (!EnsureParentDirectories(dst)) return false;

  // Horizon FsFileSystem::RenameFile rejects overwriting an existing destination.
  if (Exists(dst)) {
    if (IsDirectory(dst)) {
      if (!RemoveDirectoryAll(dst)) return false;
    } else {
      if (::unlink(dst.c_str()) != 0) return false;
    }
  }

  if (std::rename(src.c_str(), dst.c_str()) == 0) {
    MarkDirtyIfSave(src);
    MarkDirtyIfSave(dst);
    return true;
  }

  if (!IsDirectory(src) && CopyAndRemoveFile(src, dst)) {
    MarkDirtyIfSave(src);
    MarkDirtyIfSave(dst);
    return true;
  }
  return false;
}

bool SwitchFileSystem::ReadBinaryFile(std::string_view path,
                                      std::vector<uint8_t>* out) const {
  if (!out) return false;
  const std::string norm = ResolvePath(path);
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  std::FILE* f = std::fopen(norm.c_str(), "rb");
  if (!f) return false;

  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    return false;
  }
  const long sz = std::ftell(f);
  if (sz < 0) {
    std::fclose(f);
    return false;
  }
  std::rewind(f);

  out->resize(static_cast<std::size_t>(sz));
  std::size_t total_read = 0;
  while (total_read < out->size()) {
    const std::size_t n =
        std::fread(out->data() + total_read, 1, out->size() - total_read, f);
    if (n == 0) break;
    total_read += n;
  }
  const bool ok = (total_read == out->size()) && (std::ferror(f) == 0);
  std::fclose(f);
  if (!ok) out->clear();
  return ok;
}

bool SwitchFileSystem::WriteBinaryFile(std::string_view path,
                                       const uint8_t* data,
                                       size_t size) const {
  const std::string norm = ResolvePath(path);
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  if (!EnsureParentDirectories(norm)) return false;

  std::FILE* f = std::fopen(norm.c_str(), "wb");
  if (!f) return false;

  bool ok = true;
  if (size > 0 && data != nullptr) {
    std::size_t written = 0;
    while (written < size) {
      const std::size_t n = std::fwrite(data + written, 1, size - written, f);
      if (n == 0) {
        ok = false;
        break;
      }
      written += n;
    }
  }
  if (std::fclose(f) != 0) ok = false;
  if (ok) MarkDirtyIfSave(norm);
  return ok;
}

bool SwitchFileSystem::ReadTextFile(std::string_view path,
                                    std::string* out) const {
  if (!out) return false;
  std::vector<uint8_t> bytes;
  if (!ReadBinaryFile(path, &bytes)) return false;
  out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

bool SwitchFileSystem::WriteTextFile(std::string_view path,
                                     std::string_view content) const {
  return WriteBinaryFile(
      path, reinterpret_cast<const uint8_t*>(content.data()), content.size());
}

std::vector<std::string> SwitchFileSystem::ListDirectory(
    std::string_view path) const {
  std::vector<std::string> entries;
  const std::string norm = ResolvePath(path);
  std::unique_lock<std::recursive_mutex> save_lock(save_fs_mutex_,
                                                   std::defer_lock);
  if (IsSavePath(norm)) save_lock.lock();
  DIR* dir = ::opendir(norm.c_str());
  if (!dir) return entries;

  struct dirent* ent = nullptr;
  while ((ent = ::readdir(dir)) != nullptr) {
    if (std::strcmp(ent->d_name, ".") == 0 ||
        std::strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    entries.emplace_back(ent->d_name);
  }
  ::closedir(dir);
  return entries;
}

bool SwitchFileSystem::SaveScreenshotPng(std::string_view path, int32_t width,
                                         int32_t height,
                                         const uint8_t* rgba) const {
  if (!rgba || width <= 0 || height <= 0) return false;
  const std::string norm = ResolvePath(path);
  if (!EnsureParentDirectories(norm)) return false;

  const int ok = stbi_write_png(norm.c_str(), width, height, 4, rgba,
                                width * 4);
  if (ok != 0) {
    MarkDirtyIfSave(norm);
    return true;
  }
  return false;
}

bool SwitchFileSystem::DecodePngFromMemory(
    const uint8_t* data, size_t size, int32_t* out_w, int32_t* out_h,
    std::vector<uint8_t>* out_rgba) const {
  if (!data || size == 0 || !out_w || !out_h || !out_rgba) return false;
  int w = 0;
  int h = 0;
  int comp = 0;
  stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h,
                                           &comp, 4);
  if (!decoded) return false;
  *out_w = w;
  *out_h = h;
  out_rgba->assign(decoded, decoded + static_cast<std::size_t>(w) * h * 4);
  stbi_image_free(decoded);
  return true;
}

}  // namespace minicraft::platform::nx
