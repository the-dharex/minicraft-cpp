#ifndef MINICRAFT_CORE_UTIL_BOOK_DATA_H_
#define MINICRAFT_CORE_UTIL_BOOK_DATA_H_

#include <functional>
#include <string>
#include <string_view>

#include "core/pak/pak_reader.h"

namespace minicraft {
namespace util {

class BookData {
 public:
  using BookProvider = std::function<std::string()>;

  static BookData& Instance();

  void LoadFromPak(const pak::PakReader& pak);
  void ResetBooks();

  static std::string LoadBook(std::string_view content);

  void SetAbout(BookProvider p) { about_ = std::move(p); }
  void SetCredits(BookProvider p) { credits_ = std::move(p); }
  void SetInstructions(BookProvider p) { instructions_ = std::move(p); }
  void SetAntVenomBook(BookProvider p) { ant_venom_book_ = std::move(p); }
  void SetStorylineGuide(BookProvider p) { storyline_guide_ = std::move(p); }

  std::string About() const { return about_ ? about_() : ""; }
  std::string Credits() const { return credits_ ? credits_() : ""; }
  std::string Instructions() const {
    return instructions_ ? instructions_() : "";
  }
  std::string AntVenomBook() const {
    return ant_venom_book_ ? ant_venom_book_() : "";
  }
  std::string StorylineGuide() const {
    return storyline_guide_ ? storyline_guide_() : "";
  }

 private:
  BookProvider about_;
  BookProvider credits_;
  BookProvider instructions_;
  BookProvider ant_venom_book_;
  BookProvider storyline_guide_;
};

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_BOOK_DATA_H_
