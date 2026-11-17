#ifndef MINICRAFT_CORE_UTIL_LOCALIZATION_H_
#define MINICRAFT_CORE_UTIL_LOCALIZATION_H_

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/pak/pak_reader.h"

namespace minicraft {
namespace util {

struct LocaleInformation {
  std::string tag;     // Lowercase tag e.g. "en-us"
  std::string name;    // Display language name e.g. "English"
  std::string region;  // Optional region e.g. "US"

  std::string DisplayString() const;
  bool operator==(const LocaleInformation& other) const {
    return tag == other.tag && name == other.name && region == other.region;
  }
};

using FormatArg = std::variant<std::string_view, std::string, int64_t, uint64_t,
                               int32_t, uint32_t, double>;

class Localization {
 public:
  static constexpr std::string_view kDefaultLocale = "en-us";
  static constexpr std::string_view kDebugLocale = "root";

  static Localization& Instance();

  void InitFromPak(const pak::PakReader& pak);
  void ResetLocalizations();

  void AddLocale(const LocaleInformation& info);
  void AddLocalization(std::string_view tag, std::string json_text);

  void ChangeLanguage(std::string_view new_language);
  void LoadLanguage();

  const std::string& SelectedLocaleTag() const { return selected_locale_; }
  LocaleInformation SelectedLanguage() const;
  std::vector<LocaleInformation> GetLocales() const;

  std::string Translate(std::string_view key,
                        std::initializer_list<FormatArg> args = {}) const;

  // Global static convenience wrappers matching Minicraft call sites.
  static std::string GetLocalized(std::string_view key) {
    return Instance().Translate(key);
  }
  template <typename... Args>
  static std::string GetLocalized(std::string_view key, const Args&... args) {
    return Instance().Translate(key, {FormatArg(args)...});
  }

  static void SetDebugLocaleEnabled(bool enabled) {
    Instance().debug_locale_enabled_ = enabled;
  }
  static bool IsDebugLocaleEnabled() {
    return Instance().debug_locale_enabled_;
  }
  static std::string NormalizeTag(std::string_view tag);

 private:
  static std::string FormatString(std::string_view fmt,
                                  std::initializer_list<FormatArg> args);

  bool debug_locale_enabled_ = false;
  std::string selected_locale_ = std::string(kDefaultLocale);
  std::unordered_map<std::string, LocaleInformation> locale_info_;
  std::unordered_map<std::string, std::vector<std::string>>
      unloaded_localization_;
  std::unordered_map<std::string, std::string> localization_;
};

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_LOCALIZATION_H_
