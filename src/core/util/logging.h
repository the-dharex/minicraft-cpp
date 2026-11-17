#ifndef MINICRAFT_CORE_UTIL_LOGGING_H_
#define MINICRAFT_CORE_UTIL_LOGGING_H_

#include <string_view>

namespace minicraft {
namespace util {

enum class LogLevel {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

class Logger {
 public:
  explicit Logger(std::string_view tag);

  void Trace(std::string_view msg) const;
  void Debug(std::string_view msg) const;
  void Info(std::string_view msg) const;
  void Warn(std::string_view msg) const;
  void Error(std::string_view msg) const;

  static void SetMinLevel(LogLevel level);
  static LogLevel MinLevel();

 private:
  void Log(LogLevel level, std::string_view msg) const;
  const char* tag_;
};

namespace log {
extern Logger kGameHandler;
extern Logger kWorld;
extern Logger kPlayer;
extern Logger kSaveLoad;
extern Logger kSprite;
extern Logger kResourcePack;
extern Logger kSound;
extern Logger kLocalization;
extern Logger kNetwork;
}  // namespace log

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_LOGGING_H_
