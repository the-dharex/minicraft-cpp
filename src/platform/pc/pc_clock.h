#ifndef MINICRAFT_PLATFORM_PC_PC_CLOCK_H_
#define MINICRAFT_PLATFORM_PC_PC_CLOCK_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "core/platform/time_clock.h"

namespace minicraft {
namespace platform {
namespace pc {

class PcClock final : public IClock {
 public:
  int64_t NowNanoseconds() const override;
  int64_t NowMilliseconds() const override;
  std::string FormatLocalTime(std::string_view strftime_fmt) const override;
  void SleepNanoseconds(int64_t ns) const override;
};

}  // namespace pc
}  // namespace platform
}  // namespace minicraft

#endif  // MINICRAFT_PLATFORM_PC_PC_CLOCK_H_
