#ifndef MINICRAFT_CORE_IO_SETTINGS_H_
#define MINICRAFT_CORE_IO_SETTINGS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace minicraft {
namespace io {

enum class SettingKind : uint8_t {
  kBool = 0,
  kIntRange = 1,
  kArrayString = 2,
  kArrayInt = 3,
};

struct SettingEntry {
  std::string key;
  std::string loc_name;
  SettingKind kind = SettingKind::kBool;

  // Values / ranges / selection options
  bool bool_val = false;
  int32_t int_val = 0;
  int32_t int_min = 0;
  int32_t int_max = 0;
  std::vector<std::string> str_options;
  std::vector<int32_t> int_options;
  std::vector<bool> option_visible;
  size_t selected_idx = 0;
  bool visible = true;
  bool wrap = true;
  bool localize_options = true;

  std::function<void(const SettingEntry&)> on_change;

  bool IsOptionVisible(size_t idx) const {
    return idx >= option_visible.size() || option_visible[idx];
  }
  std::string DisplayValue() const;
};

class Settings {
 public:
  static Settings& Instance();

  void InitDefaults();

  static SettingEntry* GetEntry(std::string_view key);
  static bool Has(std::string_view key);

  static std::string GetString(std::string_view key);
  static int32_t GetInt(std::string_view key);
  static bool GetBool(std::string_view key);
  static size_t GetIdx(std::string_view key);

  static void Set(std::string_view key, std::string_view val);
  static void Set(std::string_view key, const char* val) {
    Set(key, std::string_view(val ? val : ""));
  }
  static void Set(std::string_view key, int32_t val);
  static void Set(std::string_view key, bool val);
  static void SetIdx(std::string_view key, size_t idx);

  static int32_t GetMaxFps();
  static void SetMaxFps(int32_t fps);

 private:
  Settings() { InitDefaults(); }

  std::unordered_map<std::string, SettingEntry> entries_;
};

}  // namespace io
}  // namespace minicraft

#endif  // MINICRAFT_CORE_IO_SETTINGS_H_
