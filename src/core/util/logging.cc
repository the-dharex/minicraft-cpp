#include "core/util/logging.h"

#include <iostream>
#include <mutex>

namespace minicraft {
namespace util {

namespace {
LogLevel g_min_level = LogLevel::kInfo;
std::mutex g_log_mutex;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "INFO";
}
}  // namespace

Logger::Logger(std::string_view tag) : tag_(tag.data()) {}

void Logger::SetMinLevel(LogLevel level) { g_min_level = level; }

LogLevel Logger::MinLevel() { return g_min_level; }

void Logger::Log(LogLevel level, std::string_view msg) const {
  if (static_cast<int>(level) < static_cast<int>(g_min_level)) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::ostream& os = (level == LogLevel::kError || level == LogLevel::kWarn)
                         ? std::cerr
                         : std::cout;
  os << "[" << LevelName(level) << "][" << tag_ << "] " << msg << "\n";
}

void Logger::Trace(std::string_view msg) const { Log(LogLevel::kTrace, msg); }
void Logger::Debug(std::string_view msg) const { Log(LogLevel::kDebug, msg); }
void Logger::Info(std::string_view msg) const { Log(LogLevel::kInfo, msg); }
void Logger::Warn(std::string_view msg) const { Log(LogLevel::kWarn, msg); }
void Logger::Error(std::string_view msg) const { Log(LogLevel::kError, msg); }

namespace log {
Logger kGameHandler("GAMEHANDLER");
Logger kWorld("WORLD");
Logger kPlayer("PLAYER");
Logger kSaveLoad("SAVELOAD");
Logger kSprite("SPRITE");
Logger kResourcePack("RESOURCEPACK");
Logger kSound("SOUND");
Logger kLocalization("LOCALIZATION");
Logger kNetwork("NETWORK");
}  // namespace log

}  // namespace util
}  // namespace minicraft
