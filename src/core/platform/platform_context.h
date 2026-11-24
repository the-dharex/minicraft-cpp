#ifndef MINICRAFT_CORE_PLATFORM_PLATFORM_CONTEXT_H_
#define MINICRAFT_CORE_PLATFORM_PLATFORM_CONTEXT_H_

namespace minicraft {
namespace platform {

class IAudioBackend;
class IClock;
class IFileSystem;
class IRenderBackend;
class IWindowBackend;

// Bundles all platform-specific backend implementations injected into core.
struct PlatformContext {
  IWindowBackend* window = nullptr;
  IRenderBackend* renderer = nullptr;
  IAudioBackend* audio = nullptr;
  IFileSystem* file_system = nullptr;
  IClock* clock = nullptr;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_PLATFORM_CONTEXT_H_
