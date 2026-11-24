#ifndef MINICRAFT_CORE_ITEM_FISHING_DATA_H_
#define MINICRAFT_CORE_ITEM_FISHING_DATA_H_

#include <string>
#include <string_view>
#include <vector>

#include "core/pak/pak_reader.h"

namespace minicraft {
namespace item {

class FishingData {
 public:
  static void LoadFromPak(const pak::PakReader& pak);

  static const std::vector<std::string>& FishData() { return fish_data_; }
  static const std::vector<std::string>& ToolData() { return tool_data_; }
  static const std::vector<std::string>& JunkData() { return junk_data_; }
  static const std::vector<std::string>& RareData() { return rare_data_; }

 private:
  static std::vector<std::string> ReadLines(const pak::PakReader& pak,
                                            std::string_view name);

  static std::vector<std::string> fish_data_;
  static std::vector<std::string> tool_data_;
  static std::vector<std::string> junk_data_;
  static std::vector<std::string> rare_data_;
};

}  // namespace item
}  // namespace minicraft

#endif  // MINICRAFT_CORE_ITEM_FISHING_DATA_H_
