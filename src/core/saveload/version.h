#ifndef MINICRAFT_CORE_SAVELOAD_VERSION_H_
#define MINICRAFT_CORE_SAVELOAD_VERSION_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace minicraft::saveload {

// Semantic version parser supporting Minicraft+ prerelease suffixes like
// "-devN" and "-infdevN".
class Version {
 public:
  static constexpr std::string_view kCurrentVersion = "2.3.0-infdev3";

  explicit Version(std::string_view version_str, bool print_error = true);
  Version(int32_t make, int32_t major, int32_t minor, int32_t dev = 0,
          bool is_infdev = false);

  static Version Current() { return Version(kCurrentVersion); }

  bool IsValid() const { return valid_; }
  int32_t Make() const { return make_; }
  int32_t Major() const { return major_; }
  int32_t Minor() const { return minor_; }
  int32_t Dev() const { return dev_; }
  bool IsInfdev() const { return is_infdev_; }

  int32_t CompareTo(const Version& ov) const;
  std::string ToString() const;

  bool operator==(const Version& other) const {
    return CompareTo(other) == 0;
  }
  bool operator!=(const Version& other) const {
    return CompareTo(other) != 0;
  }
  bool operator<(const Version& other) const { return CompareTo(other) < 0; }
  bool operator<=(const Version& other) const {
    return CompareTo(other) <= 0;
  }
  bool operator>(const Version& other) const { return CompareTo(other) > 0; }
  bool operator>=(const Version& other) const {
    return CompareTo(other) >= 0;
  }

 private:
  int32_t make_ = 0;
  int32_t major_ = 0;
  int32_t minor_ = 0;
  int32_t dev_ = 0;
  bool is_infdev_ = false;
  bool valid_ = false;
};

}  // namespace minicraft::saveload

#endif  // MINICRAFT_CORE_SAVELOAD_VERSION_H_
