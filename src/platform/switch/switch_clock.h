#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "core/platform/time_clock.h"

namespace minicraft::platform::nx {

// Monotonic high-resolution clock backed by ARM system counter (`armGetSystemTick`)
// with Home-Menu / Sleep suspend compensation, plus local calendar formatting via
// Switch time service (`timeToCalendarTimeWithMyRule`).
class SwitchClock final : public IClock {
 public:
  SwitchClock();
  ~SwitchClock() override = default;

  int64_t NowNanoseconds() const override;
  int64_t NowMilliseconds() const override;
  std::string FormatLocalTime(std::string_view strftime_fmt) const override;
  void SleepNanoseconds(int64_t ns) const override;

  // Freezes monotonic elapsed time while the application is unfocused/suspended
  // so gameplay timers and animations do not jump when returning from Home Menu.
  void SetPaused(bool paused);

 private:
  static int64_t RawNowNanoseconds();

  mutable std::mutex mutex_;
  int64_t start_raw_ns_ = 0;
  int64_t paused_accum_ns_ = 0;
  int64_t pause_start_raw_ns_ = 0;
  bool paused_ = false;
};

}  // namespace minicraft::platform::nx
