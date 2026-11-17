#include "core/util/localization.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <utility>

#include "core/util/logging.h"
#include "json.hpp"

namespace minicraft {
namespace util {

namespace {

bool IsBlankAscii(std::string_view s) {
  for (char c : s) {
    if (c != ' ') return false;
  }
  return true;
}

bool IsNumericAscii(std::string_view s) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[i] == '+' || s[i] == '-') ++i;
  if (i >= s.size()) return false;
  bool seen_digit = false;
  bool seen_dot = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      seen_digit = true;
    } else if (c == '.' && !seen_dot) {
      seen_dot = true;
    } else {
      return false;
    }
  }
  return seen_digit;
}

std::string ArgToString(const FormatArg& arg) {
  return std::visit(
      [](const auto& val) -> std::string {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
          return std::string(val);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return val;
        } else if constexpr (std::is_same_v<T, double>) {
          char buf[64];
          std::snprintf(buf, sizeof(buf), "%g", val);
          return buf;
        } else {
          return std::to_string(val);
        }
      },
      arg);
}

int64_t ArgToInt(const FormatArg& arg) {
  return std::visit(
      [](const auto& val) -> int64_t {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string_view> ||
                      std::is_same_v<T, std::string>) {
          return 0;
        } else {
          return static_cast<int64_t>(val);
        }
      },
      arg);
}

}  // namespace

std::string LocaleInformation::DisplayString() const {
  if (region.empty()) return name;
  return name + " (" + region + ")";
}

Localization& Localization::Instance() {
  static Localization instance;
  return instance;
}

std::string Localization::NormalizeTag(std::string_view tag) {
  std::string out(tag);
  for (char& c : out) {
    c = (c == '_') ? '-'
                 : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

void Localization::InitFromPak(const pak::PakReader& pak) {
  ResetLocalizations();

  std::string pack_text;
  if (pak.ReadTextEntry("pack.json", &pack_text)) {
    try {
      auto pack = nlohmann::json::parse(pack_text);
      if (pack.contains("language") && pack["language"].is_object()) {
        for (auto& [raw_tag, info_json] : pack["language"].items()) {
          std::string tag = NormalizeTag(raw_tag);
          LocaleInformation info;
          info.tag = tag;
          info.name = info_json.value("name", tag);
          info.region = info_json.value("region", "");
          AddLocale(info);
        }
      }
    } catch (const std::exception& e) {
      log::kLocalization.Error(std::string("Failed parsing pack.json: ") + e.what());
    }
  }

  // Fallback check if pack.json wasn't loaded.
  if (locale_info_.find(std::string(kDefaultLocale)) == locale_info_.end()) {
    AddLocale({std::string(kDefaultLocale), "English", "US"});
  }

  for (std::string_view entry : pak.ListEntries()) {
    if (entry.rfind("assets/localization/", 0) != 0) continue;
    if (entry.size() > 5 && entry.substr(entry.size() - 5) == ".json") {
      size_t slash = entry.rfind('/');
      std::string filename(
          slash == std::string_view::npos ? entry : entry.substr(slash + 1));
      std::string tag = NormalizeTag(filename.substr(0, filename.size() - 5));
      std::string json_text;
      if (pak.ReadTextEntry(entry, &json_text)) {
        AddLocalization(tag, std::move(json_text));
      }
    }
  }

  LoadLanguage();
}

void Localization::ResetLocalizations() {
  unloaded_localization_.clear();
  locale_info_.clear();
  if (debug_locale_enabled_) {
    locale_info_[std::string(kDebugLocale)] = {std::string(kDebugLocale),
                                               "Debug", ""};
  }
}

void Localization::AddLocale(const LocaleInformation& info) {
  LocaleInformation normalized = info;
  normalized.tag = NormalizeTag(info.tag);
  if (locale_info_.find(normalized.tag) == locale_info_.end()) {
    locale_info_.emplace(normalized.tag, std::move(normalized));
  }
}

void Localization::AddLocalization(std::string_view tag,
                                   std::string json_text) {
  std::string norm = NormalizeTag(tag);
  if (locale_info_.find(norm) == locale_info_.end()) return;
  unloaded_localization_[norm].push_back(std::move(json_text));
}

void Localization::ChangeLanguage(std::string_view new_language) {
  std::string norm = NormalizeTag(new_language);
  selected_locale_ = norm.empty() ? std::string(kDefaultLocale) : norm;
  LoadLanguage();
}

void Localization::LoadLanguage() {
  localization_.clear();
  if (selected_locale_ == kDebugLocale) return;

  if (unloaded_localization_.find(selected_locale_) ==
      unloaded_localization_.end()) {
    selected_locale_ = std::string(kDefaultLocale);
  }

  auto load_list = [&](const std::string& tag, bool overwrite) {
    auto it = unloaded_localization_.find(tag);
    if (it == unloaded_localization_.end()) return;
    for (const std::string& text : it->second) {
      try {
        auto json = nlohmann::json::parse(text);
        if (!json.is_object()) continue;
        for (auto& [k, v] : json.items()) {
          if (v.is_string() && (overwrite || localization_.find(k) ==
                                                 localization_.end())) {
            localization_[k] = v.get<std::string>();
          }
        }
      } catch (const std::exception& e) {
        log::kLocalization.Error(
            std::string("Failed parsing localization JSON: ") + e.what());
      }
    }
  };

  load_list(selected_locale_, true);
  if (selected_locale_ != kDefaultLocale) {
    load_list(std::string(kDefaultLocale), false);
  }
}

LocaleInformation Localization::SelectedLanguage() const {
  auto it = locale_info_.find(selected_locale_);
  if (it != locale_info_.end()) return it->second;
  return {selected_locale_, selected_locale_, ""};
}

std::vector<LocaleInformation> Localization::GetLocales() const {
  std::vector<LocaleInformation> out;
  out.reserve(locale_info_.size());
  for (const auto& [k, v] : locale_info_) {
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(),
            [](const LocaleInformation& a, const LocaleInformation& b) {
              return a.DisplayString() < b.DisplayString();
            });
  return out;
}

std::string Localization::Translate(
    std::string_view key, std::initializer_list<FormatArg> args) const {
  if (IsBlankAscii(key)) return std::string(key);
  if (selected_locale_ == kDebugLocale) return std::string(key);
  if (IsNumericAscii(key)) return std::string(key);

  auto it = localization_.find(std::string(key));
  if (it == localization_.end()) {
    return std::string(key);
  }
  if (args.size() == 0) {
    return it->second;
  }
  return FormatString(it->second, args);
}

std::string Localization::FormatString(std::string_view fmt,
                                       std::initializer_list<FormatArg> args) {
  std::string out;
  out.reserve(fmt.size() + 32);
  auto it = args.begin();

  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] != '%' || i + 1 >= fmt.size()) {
      out.push_back(fmt[i]);
      continue;
    }

    if (fmt[i + 1] == '%') {
      out.push_back('%');
      ++i;
      continue;
    }

    // Parse simple printf flags/width: %s, %d, %02d, %.2f
    size_t spec_start = i + 1;
    bool zero_pad = false;
    int width = 0;
    size_t j = spec_start;
    if (j < fmt.size() && fmt[j] == '0') {
      zero_pad = true;
      ++j;
    }
    while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') {
      width = width * 10 + (fmt[j] - '0');
      ++j;
    }
    if (j < fmt.size() && fmt[j] == '.') {
      ++j;
      while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') ++j;
    }

    if (j < fmt.size() && (fmt[j] == 's' || fmt[j] == 'd' || fmt[j] == 'f')) {
      if (it != args.end()) {
        if (fmt[j] == 'd') {
          std::string num = std::to_string(ArgToInt(*it));
          if (width > static_cast<int>(num.size())) {
            out.append(static_cast<size_t>(width - num.size()),
                       zero_pad ? '0' : ' ');
          }
          out.append(num);
        } else {
          out.append(ArgToString(*it));
        }
        ++it;
      } else {
        out.append(fmt.substr(i, j - i + 1));
      }
      i = j;
    } else {
      out.push_back('%');
    }
  }
  return out;
}

}  // namespace util
}  // namespace minicraft
