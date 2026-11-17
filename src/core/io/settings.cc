#include "core/io/settings.h"

#include <algorithm>
#include <utility>

namespace minicraft {
namespace io {

std::string SettingEntry::DisplayValue() const {
  switch (kind) {
    case SettingKind::kBool:
      return bool_val ? "minicraft.display.entries.boolean.true"
                      : "minicraft.display.entries.boolean.false";
    case SettingKind::kIntRange:
      return std::to_string(int_val);
    case SettingKind::kArrayString:
      if (str_options.empty()) return "";
      return str_options[std::min(selected_idx, str_options.size() - 1)];
    case SettingKind::kArrayInt:
      if (int_options.empty()) return "0";
      return std::to_string(
          int_options[std::min(selected_idx, int_options.size() - 1)]);
  }
  return "";
}

Settings& Settings::Instance() {
  static Settings instance;
  return instance;
}

void Settings::InitDefaults() {
  entries_.clear();

  auto add_bool = [&](std::string key, std::string loc, bool def) {
    SettingEntry e;
    e.key = std::move(key);
    e.loc_name = std::move(loc);
    e.kind = SettingKind::kBool;
    e.bool_val = def;
    entries_[e.key] = std::move(e);
  };

  auto add_range = [&](std::string key, std::string loc, int32_t min_v,
                       int32_t max_v, int32_t def) {
    SettingEntry e;
    e.key = std::move(key);
    e.loc_name = std::move(loc);
    e.kind = SettingKind::kIntRange;
    e.int_min = min_v;
    e.int_max = max_v;
    e.int_val = std::clamp(def, min_v, max_v);
    e.wrap = false;
    e.localize_options = false;
    entries_[e.key] = std::move(e);
  };

  auto add_str_arr = [&](std::string key, std::string loc,
                         std::vector<std::string> opts, size_t def_idx,
                         bool visible = true) {
    SettingEntry e;
    e.key = std::move(key);
    e.loc_name = std::move(loc);
    e.kind = SettingKind::kArrayString;
    e.str_options = std::move(opts);
    e.selected_idx = def_idx;
    e.visible = visible;
    entries_[e.key] = std::move(e);
  };

  auto add_int_arr = [&](std::string key, std::string loc,
                         std::vector<int32_t> opts, size_t def_idx) {
    SettingEntry e;
    e.key = std::move(key);
    e.loc_name = std::move(loc);
    e.kind = SettingKind::kArrayInt;
    e.int_options = std::move(opts);
    e.selected_idx = def_idx;
    entries_[e.key] = std::move(e);
  };

  add_bool("sound", "minicraft.settings.sound", true);
  add_bool("autosave", "minicraft.settings.autosave", true);
  add_range("fps", "minicraft.settings.fps", 10, 300, 60);
  add_bool("skinon", "minicraft.displays.skin", false);
  add_bool("fullscreen", "Fullscreen", false);

  add_bool("tutorials", "minicraft.settings.tutorials", false);
  add_bool("quests", "minicraft.settings.quests", false);
  add_bool("showquests", "minicraft.settings.show_quests", true);

  add_str_arr("diff", "minicraft.settings.difficulty",
              {"minicraft.settings.difficulty.easy",
               "minicraft.settings.difficulty.normal",
               "minicraft.settings.difficulty.hard"},
              1);

  add_str_arr("mode", "minicraft.settings.mode",
              {"minicraft.settings.mode.survival",
               "minicraft.settings.mode.creative",
               "minicraft.settings.mode.hardcore",
               "minicraft.settings.mode.score"},
              0);

  {
    SettingEntry st;
    st.key = "scoretime";
    st.loc_name = "minicraft.settings.scoretime";
    st.kind = SettingKind::kArrayInt;
    st.int_options = {10, 20, 40, 60, 120};
    st.option_visible = {false, true, true, true, false};
    st.selected_idx = 1;
    st.visible = false;
    entries_[st.key] = std::move(st);
  }
  auto mode_it = entries_.find("mode");
  if (mode_it != entries_.end()) {
    mode_it->second.on_change = [](const SettingEntry& e) {
      if (auto* st = GetEntry("scoretime")) {
        st->visible = (e.DisplayValue() == "minicraft.settings.mode.score");
      }
    };
    auto st_it = entries_.find("scoretime");
    if (st_it != entries_.end()) {
      st_it->second.visible =
          (mode_it->second.DisplayValue() == "minicraft.settings.mode.score");
    }
  }

  add_int_arr("size", "minicraft.settings.size", {128, 256, 512}, 0);

  add_str_arr("theme", "minicraft.settings.theme",
              {"minicraft.settings.theme.normal",
               "minicraft.settings.theme.forest",
               "minicraft.settings.theme.desert",
               "minicraft.settings.theme.plain",
               "minicraft.settings.theme.hell"},
              0);

  add_str_arr("type", "minicraft.settings.type",
              {"minicraft.settings.type.island",
               "minicraft.settings.type.box",
               "minicraft.settings.type.mountain",
               "minicraft.settings.type.irregular"},
              0);

  add_str_arr("unlocked", "unlocked", {"0"}, 0, false);
}

SettingEntry* Settings::GetEntry(std::string_view key) {
  auto& m = Instance().entries_;
  auto it = m.find(std::string(key));
  return it != m.end() ? &it->second : nullptr;
}

bool Settings::Has(std::string_view key) { return GetEntry(key) != nullptr; }

std::string Settings::GetString(std::string_view key) {
  const SettingEntry* e = GetEntry(key);
  return e ? e->DisplayValue() : "";
}

int32_t Settings::GetInt(std::string_view key) {
  const SettingEntry* e = GetEntry(key);
  if (!e) return 0;
  if (e->kind == SettingKind::kIntRange) return e->int_val;
  if (e->kind == SettingKind::kArrayInt && !e->int_options.empty()) {
    return e->int_options[std::min(e->selected_idx, e->int_options.size() - 1)];
  }
  return 0;
}

bool Settings::GetBool(std::string_view key) {
  const SettingEntry* e = GetEntry(key);
  return e ? e->bool_val : false;
}

size_t Settings::GetIdx(std::string_view key) {
  const SettingEntry* e = GetEntry(key);
  return e ? e->selected_idx : 0;
}

void Settings::Set(std::string_view key, std::string_view val) {
  SettingEntry* e = GetEntry(key);
  if (!e) return;
  if (e->kind == SettingKind::kBool) {
    e->bool_val = (val == "true" || val == "1");
  } else if (e->kind == SettingKind::kArrayString) {
    for (size_t i = 0; i < e->str_options.size(); ++i) {
      if (e->str_options[i] == val) {
        e->selected_idx = i;
        break;
      }
    }
  }
  if (e->on_change) e->on_change(*e);
}

void Settings::Set(std::string_view key, int32_t val) {
  SettingEntry* e = GetEntry(key);
  if (!e) return;
  if (e->kind == SettingKind::kIntRange) {
    e->int_val = std::clamp(val, e->int_min, e->int_max);
  } else if (e->kind == SettingKind::kArrayInt) {
    for (size_t i = 0; i < e->int_options.size(); ++i) {
      if (e->int_options[i] == val) {
        e->selected_idx = i;
        break;
      }
    }
  }
  if (e->on_change) e->on_change(*e);
}

void Settings::Set(std::string_view key, bool val) {
  SettingEntry* e = GetEntry(key);
  if (!e) return;
  e->bool_val = val;
  if (e->on_change) e->on_change(*e);
}

void Settings::SetIdx(std::string_view key, size_t idx) {
  SettingEntry* e = GetEntry(key);
  if (!e) return;
  if (e->kind == SettingKind::kArrayString && !e->str_options.empty()) {
    e->selected_idx = idx % e->str_options.size();
  } else if (e->kind == SettingKind::kArrayInt && !e->int_options.empty()) {
    e->selected_idx = idx % e->int_options.size();
  } else if (e->kind == SettingKind::kBool) {
    e->bool_val = (idx != 0);
  }
  if (e->on_change) e->on_change(*e);
}

int32_t Settings::GetMaxFps() { return GetInt("fps"); }

void Settings::SetMaxFps(int32_t fps) { Set("fps", fps); }

}  // namespace io
}  // namespace minicraft
