#include "core/util/book_data.h"

#include <utility>

namespace minicraft {
namespace util {

BookData& BookData::Instance() {
  static BookData instance;
  return instance;
}

std::string BookData::LoadBook(std::string_view content) {
  std::string out;
  out.reserve(content.size());
  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == '\\' && i + 1 < content.size() && content[i + 1] == '0') {
      out.push_back('\0');
      ++i;
    } else if (content[i] != '\r') {
      out.push_back(content[i]);
    }
  }
  return out;
}

void BookData::ResetBooks() {
  about_ = {};
  credits_ = {};
  instructions_ = {};
  ant_venom_book_ = {};
  storyline_guide_ = {};
}

void BookData::LoadFromPak(const pak::PakReader& pak) {
  ResetBooks();
  auto bind_pak_book = [&pak](std::string_view path) -> BookProvider {
    std::string raw;
    if (!pak.ReadTextEntry(path, &raw)) return {};
    std::string loaded = LoadBook(raw);
    return [loaded = std::move(loaded)]() { return loaded; };
  };

  about_ = bind_pak_book("assets/books/about.txt");
  credits_ = bind_pak_book("assets/books/credits.txt");
  instructions_ = bind_pak_book("assets/books/instructions.txt");
  ant_venom_book_ = bind_pak_book("assets/books/antidous.txt");
  storyline_guide_ = bind_pak_book("assets/books/game_guide.txt");
}

}  // namespace util
}  // namespace minicraft
