#include "platform/pc/pc_clock.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace minicraft {
namespace platform {
namespace pc {

int64_t PcClock::NowNanoseconds() const {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

int64_t PcClock::NowMilliseconds() const {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string PcClock::FormatLocalTime(std::string_view strftime_fmt) const {
  std::time_t t = std::time(nullptr);
  std::tm tm_val{};
#ifdef _WIN32
  localtime_s(&tm_val, &t);
#else
  localtime_r(&t, &tm_val);
#endif
  char buf[128] = {};
  std::string fmt(strftime_fmt);
  std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_val);
  return std::string(buf);
}

void PcClock::SleepNanoseconds(int64_t ns) const {
  if (ns <= 0) {
    return;
  }
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

}  // namespace pc
}  // namespace platform
}  // namespace minicraft
