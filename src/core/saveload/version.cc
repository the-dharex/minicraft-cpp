#include "core/saveload/version.h"

#include <charconv>
#include <string>

#include "core/util/logging.h"

namespace minicraft::saveload {

namespace {

bool ParseIntStrict(std::string_view sv, int32_t* out) {
  if (sv.empty()) return false;
  const char* begin = sv.data();
  const char* end = sv.data() + sv.size();
  auto [ptr, ec] = std::from_chars(begin, end, *out);
  return ec == std::errc() && ptr == end;
}

}  // namespace

Version::Version(std::string_view version_str, bool print_error) {
  std::string cleaned;
  cleaned.reserve(version_str.size());
  for (char c : version_str) {
    if (c == '-') {
      break;
    }
    cleaned.push_back(c);
  }

  std::string_view remainder;
  const size_t dash_pos = version_str.find('-');
  if (dash_pos != std::string_view::npos) {
    remainder = version_str.substr(dash_pos + 1);
  }

  // Split X.Y.Z
  size_t dot1 = cleaned.find('.');
  size_t dot2 =
      (dot1 == std::string::npos) ? std::string::npos : cleaned.find('.', dot1 + 1);
  if (dot1 == std::string::npos || dot2 == std::string::npos ||
      cleaned.find('.', dot2 + 1) != std::string::npos) {
    if (print_error) {
      util::log::kSaveLoad.Error("INVALID version number format: \"" +
                                 std::string(version_str) + "\"");
    }
    valid_ = false;
    return;
  }

  int32_t mk = 0, mj = 0, mn = 0;
  if (!ParseIntStrict(std::string_view(cleaned).substr(0, dot1), &mk) ||
      !ParseIntStrict(std::string_view(cleaned).substr(dot1 + 1, dot2 - dot1 - 1),
                      &mj) ||
      !ParseIntStrict(std::string_view(cleaned).substr(dot2 + 1), &mn)) {
    if (print_error) {
      util::log::kSaveLoad.Error("INVALID version number \"" +
                                 std::string(version_str) + "\"");
    }
    valid_ = false;
    return;
  }

  int32_t dv = 0;
  bool infdev = false;
  if (!remainder.empty()) {
    if (remainder.rfind("infdev", 0) == 0) {
      infdev = true;
      if (!ParseIntStrict(remainder.substr(6), &dv)) {
        if (print_error) {
          util::log::kSaveLoad.Warn("Failed to parse infdev number in \"" +
                                    std::string(version_str) + "\"");
        }
        dv = 1;
      }
    } else if (remainder.rfind("dev", 0) == 0) {
      if (!ParseIntStrict(remainder.substr(3), &dv)) {
        if (print_error) {
          util::log::kSaveLoad.Warn("Failed to parse dev number in \"" +
                                    std::string(version_str) + "\"");
        }
        dv = 1;
      }
    }
  }

  make_ = mk;
  major_ = mj;
  minor_ = mn;
  dev_ = dv;
  is_infdev_ = infdev;
  valid_ = true;
}

Version::Version(int32_t make, int32_t major, int32_t minor, int32_t dev,
                 bool is_infdev)
    : make_(make),
      major_(major),
      minor_(minor),
      dev_(dev),
      is_infdev_(is_infdev),
      valid_(true) {}

int32_t Version::CompareTo(const Version& ov) const {
  if (make_ != ov.make_) return make_ < ov.make_ ? -1 : 1;
  if (major_ != ov.major_) return major_ < ov.major_ ? -1 : 1;
  if (minor_ != ov.minor_) return minor_ < ov.minor_ ? -1 : 1;

  if (dev_ != ov.dev_ || is_infdev_ != ov.is_infdev_) {
    // dev == 0 means full release, which is newer than any -dev/-infdev build.
    if (dev_ == 0) return 1;
    if (ov.dev_ == 0) return -1;
    if (is_infdev_ != ov.is_infdev_) {
      // infdev comes after dev within the same major.minor.
      return is_infdev_ ? 1 : -1;
    }
    return dev_ < ov.dev_ ? -1 : 1;
  }
  return 0;
}

std::string Version::ToString() const {
  std::string s = std::to_string(make_) + "." + std::to_string(major_) + "." +
                  std::to_string(minor_);
  if (dev_ > 0) {
    s += is_infdev_ ? "-infdev" : "-dev";
    s += std::to_string(dev_);
  }
  return s;
}

}  // namespace minicraft::saveload
