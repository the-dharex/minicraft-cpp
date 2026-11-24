#include "core/item/fishing_data.h"

#include <sstream>

namespace minicraft {
namespace item {

std::vector<std::string> FishingData::fish_data_;
std::vector<std::string> FishingData::tool_data_;
std::vector<std::string> FishingData::junk_data_;
std::vector<std::string> FishingData::rare_data_;

void FishingData::LoadFromPak(const pak::PakReader& pak) {
  fish_data_ = ReadLines(pak, "fish");
  tool_data_ = ReadLines(pak, "tool");
  junk_data_ = ReadLines(pak, "junk");
  rare_data_ = ReadLines(pak, "rare");
}

std::vector<std::string> FishingData::ReadLines(const pak::PakReader& pak,
                                                std::string_view name) {
  std::vector<std::string> out;
  std::string path =
      "resources/data/fishing/" + std::string(name) + "_loot.txt";
  std::string text;
  if (!pak.ReadTextEntry(path, &text)) return out;

  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) out.push_back(std::move(line));
  }
  return out;
}

}  // namespace item
}  // namespace minicraft
