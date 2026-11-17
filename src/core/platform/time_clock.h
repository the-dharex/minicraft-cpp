#ifndef MINICRAFT_CORE_PLATFORM_TIME_CLOCK_H_
#define MINICRAFT_CORE_PLATFORM_TIME_CLOCK_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace minicraft {
namespace platform {

// platform-agnostic high-resolution clock & sleep interface.
class IClock {
 public:
  virtual ~IClock() = default;

  virtual int64_t NowNanoseconds() const = 0;
  virtual int64_t NowMilliseconds() const = 0;
  virtual std::string FormatLocalTime(std::string_view strftime_fmt) const = 0;
  virtual void SleepNanoseconds(int64_t ns) const = 0;
};

}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_CORE_PLATFORM_TIME_CLOCK_H_
