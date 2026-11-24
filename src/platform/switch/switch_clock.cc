#include "platform/switch/switch_clock.h"

#include <switch.h>

#include <ctime>
#include <string>

namespace minicraft::platform::nx {

int64_t SwitchClock::RawNowNanoseconds() {
  return static_cast<int64_t>(armTicksToNs(armGetSystemTick()));
}

SwitchClock::SwitchClock() : start_raw_ns_(RawNowNanoseconds()) {}

void SwitchClock::SetPaused(bool paused) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (paused == paused_) {
    return;
  }
  const int64_t now = RawNowNanoseconds();
  if (paused) {
    pause_start_raw_ns_ = now;
    paused_ = true;
  } else {
    paused_accum_ns_ += (now - pause_start_raw_ns_);
    paused_ = false;
  }
}

int64_t SwitchClock::NowNanoseconds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t effective_raw = paused_ ? pause_start_raw_ns_ : RawNowNanoseconds();
  const int64_t elapsed = effective_raw - start_raw_ns_ - paused_accum_ns_;
  return elapsed > 0 ? elapsed : 0;
}

int64_t SwitchClock::NowMilliseconds() const {
  return NowNanoseconds() / 1000000;
}

std::string SwitchClock::FormatLocalTime(std::string_view strftime_fmt) const {
  u64 posix_time = 0;
  Result rc = timeGetCurrentTime(TimeType_LocalSystemClock, &posix_time);
  if (R_FAILED(rc)) {
    posix_time = static_cast<u64>(std::time(nullptr));
  }

  TimeCalendarTime cal{};
  TimeCalendarAdditionalInfo cal_info{};
  rc = timeToCalendarTimeWithMyRule(posix_time, &cal, &cal_info);

  std::tm tm_val{};
  if (R_SUCCEEDED(rc)) {
    tm_val.tm_year = static_cast<int>(cal.year) - 1900;
    tm_val.tm_mon = static_cast<int>(cal.month) - 1;
    tm_val.tm_mday = static_cast<int>(cal.day);
    tm_val.tm_hour = static_cast<int>(cal.hour);
    tm_val.tm_min = static_cast<int>(cal.minute);
    tm_val.tm_sec = static_cast<int>(cal.second);
    tm_val.tm_wday = static_cast<int>(cal_info.wday);
    tm_val.tm_yday = static_cast<int>(cal_info.yday);
    tm_val.tm_isdst = static_cast<int>(cal_info.DST);
  } else {
    const std::time_t fallback = static_cast<std::time_t>(posix_time);
    localtime_r(&fallback, &tm_val);
  }

  const std::string fmt_str(strftime_fmt);
  char buf[128]{};
  const std::size_t n = std::strftime(buf, sizeof(buf), fmt_str.c_str(), &tm_val);
  return std::string(buf, n);
}

void SwitchClock::SleepNanoseconds(int64_t ns) const {
  if (ns > 0) {
    svcSleepThread(ns);
  }
}

}  // namespace minicraft::platform::nx
